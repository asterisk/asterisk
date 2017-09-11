"""add pjsip asymmetric rtp codec

Revision ID: 4468b4a91372
Revises: a6ef36f1309
Create Date: 2016-10-25 10:57:20.808815

"""

# revision identifiers, used by Alembic.
revision = '4468b4a91372'
down_revision = 'a6ef36f1309'

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

    op.add_column('ps_endpoints', sa.Column('asymmetric_rtp_codec', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_asymmetric_rtp_codec_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'asymmetric_rtp_codec')
