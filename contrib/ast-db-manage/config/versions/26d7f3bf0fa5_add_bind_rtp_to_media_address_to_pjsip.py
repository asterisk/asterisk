"""add bind_rtp_to_media_address to pjsip

Revision ID: 26d7f3bf0fa5
Revises: 2d078ec071b7
Create Date: 2016-01-07 12:23:42.894400

"""

# revision identifiers, used by Alembic.
revision = '26d7f3bf0fa5'
down_revision = '2d078ec071b7'

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

    op.add_column('ps_endpoints', sa.Column('bind_rtp_to_media_address', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_bind_rtp_to_media_address_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'bind_rtp_to_media_address')
