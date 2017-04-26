"""create rls table

Revision ID: 1d0e332c32af
Revises: 2da192dbbc65
Create Date: 2017-04-25 12:50:09.412662

"""

# revision identifiers, used by Alembic.
revision = '1d0e332c32af'
down_revision = '2da192dbbc65'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    ############################# Enums ##############################

    # yesno_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.create_table(
        'ps_resource_list',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('list_item', sa.String(2048)),
        sa.Column('event', sa.String(40)),
        sa.Column('full_state', yesno_values),
        sa.Column('notification_batch_interval', sa.Integer),
    )

    op.create_index('ps_resource_list_id', 'ps_resource_list', ['id'])

def downgrade():
    op.drop_table('ps_resource_list')
