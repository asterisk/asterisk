"""add srv_lookups to identify

Revision ID: 28ab27a7826d
Revises: 4468b4a91372
Create Date: 2017-01-06 14:53:38.829655

"""

# revision identifiers, used by Alembic.
revision = '28ab27a7826d'
down_revision = '4468b4a91372'

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

    op.add_column('ps_endpoint_id_ips', sa.Column('srv_lookups', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoint_id_ips_srv_lookups_yesno_values','ps_endpoint_id_ips')
    op.drop_column('ps_endpoint_id_ips', 'srv_lookups')
