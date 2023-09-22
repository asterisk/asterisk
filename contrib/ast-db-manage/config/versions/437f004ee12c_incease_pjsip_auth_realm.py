"""incease pjsip auth realm

Revision ID: 437f004ee12c
Revises: a6ef36f1309
Create Date: 2023-09-23 01:24:04.668434

"""

# revision identifiers, used by Alembic.
revision = '437f004ee12c'
down_revision = 'a6ef36f1309'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_auths', 'realm', type_=sa.String(255))


def downgrade():
    op.alter_column('ps_auths', 'realm', type_=sa.String(40))
